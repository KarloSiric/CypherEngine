//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_ObjectTools.cpp
//  Purpose: Implements the everyday object commands from the Hammer 5 and
//           TrenchBroom parity review.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_ObjectTools.h"

#include "CypherMapGui_Notice.h"
#include "CypherMapGui_ToolGestures.h"
#include "CypherMap_EntityEdit.h"

#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"

#include <QHash>
#include <QPointer>
#include <QString>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace cypher::editor::map
{
using namespace common;

namespace
{
using vec3 = math::vec3d_t;

struct pointer_t {
    bool bRay{ false };   // The last pointer movement was in the 3D view.
    vec3 origin{}, direction{};
    bool bPoint{ false }; // ... or in a 2D view.
    vec3 point{};
    u32 u{ 0 }, v{ 1 };
};
QHash<const map_workspace_t *, pointer_t> &Pointers()
{
    static QHash<const map_workspace_t *, pointer_t> pointers;
    return pointers;
}

// Screen positions of the corners a move may snap onto, built at the
// drag's first snap and kept until another drag (or another wireframe).
struct vertex_snap_t {
    const void *pDrag{ nullptr };
    QPointF start{};
    const vec3 *pPoints{ nullptr };
    usize nPoints{ 0 };
    f64 cell{ 8.0 };
    std::vector<vec3> targets;
    QHash<quint64, std::vector<u32>> grid; // Cell -> targets.
    bool bSnapped{ false };
    vec3 to{};
};
QHash<const map_workspace_t *, vertex_snap_t> &VertexSnaps()
{
    static QHash<const map_workspace_t *, vertex_snap_t> snaps;
    return snaps;
}
quint64 CellKey( QPointF p, f64 cell ) noexcept
{
    const auto x = static_cast<qint32>( std::floor( p.x() / cell ) ), y = static_cast<qint32>( std::floor( p.y() / cell ) );
    return ( static_cast<quint64>( static_cast<quint32>( x ) ) << 32 ) | static_cast<quint32>( y );
}

struct entity_pick_t {
    QPointer<QObject> owner;
    map_entity_pick_fn pick;
};
QHash<const map_workspace_t *, entity_pick_t> &Picks()
{
    static QHash<const map_workspace_t *, entity_pick_t> picks;
    return picks;
}

map_workspace_t *Ws( void *pContext ) noexcept { return static_cast<map_workspace_t *>( pContext ); }
u32 State( bool enabled ) noexcept { return enabled ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE; }
command_result_t Result( bool done ) noexcept { return done ? command_result_t::OK : command_result_t::FAILED; }

f64 Comp( vec3 p, u32 axis ) noexcept { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
void SetComp( vec3 &p, u32 axis, f64 value ) noexcept { ( axis == 0 ? p.x : axis == 1 ? p.y : p.z ) = value; }
f64 Dot( vec3 a, vec3 b ) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
vec3 Sub( vec3 a, vec3 b ) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
vec3 Add( vec3 a, vec3 b ) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
vec3 Mul( vec3 a, f64 s ) noexcept { return { a.x * s, a.y * s, a.z * s }; }

bool ObjectMode( const map_workspace_t *ws ) noexcept
{
    return ws->elementMode == map_element_mode_t::OBJECTS || ws->elementMode == map_element_mode_t::GROUPS || ws->elementMode == map_element_mode_t::MESHES;
}

bool Ready( const map_workspace_t *ws ) noexcept
{
    return ws != nullptr && ws->pDocument != nullptr && !ws->pDocument->bReadOnly && !ws->editPreview.bActive &&
        !EditorHistory_IsTransactionOpen( &ws->history );
}

// Where the point projects onto the face's plane along `axis` lies inside
// the face, seen down that axis.
bool InsideAlong( const map_wireframe_t &wire, const map_wire_face_t &face, vec3 point, u32 axis ) noexcept
{
    const u32 i0 = axis == 0 ? 1u : 0u, i1 = axis == 2 ? 1u : 2u;
    bool inside = false;
    for ( u32 k = 0, j = face.nIndices - 1; k < face.nIndices; j = k++ ) {
        const vec3 a = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + k]];
        const vec3 b = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + j]];
        const f64 ay = Comp( a, i1 ), by = Comp( b, i1 );
        if ( ( ay > Comp( point, i1 ) ) != ( by > Comp( point, i1 ) ) &&
             Comp( point, i0 ) < ( Comp( b, i0 ) - Comp( a, i0 ) ) * ( Comp( point, i1 ) - ay ) / ( by - ay ) + Comp( a, i0 ) ) { inside = !inside; }
    }
    return inside;
}

// Distance along the ray to the face, or a negative number when it misses.
f64 RayFace( const map_wireframe_t &wire, const map_wire_face_t &face, vec3 origin, vec3 direction ) noexcept
{
    if ( face.nIndices < 3 ) { return -1.0; }
    const vec3 p0 = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex]];
    const f64 denominator = Dot( face.normal, direction );
    if ( std::fabs( denominator ) < 1e-12 ) { return -1.0; }
    const f64 t = Dot( face.normal, Sub( p0, origin ) ) / denominator;
    if ( t <= 0.0 || !std::isfinite( t ) ) { return -1.0; }
    const vec3 hit = Add( origin, Mul( direction, t ) );
    const f64 ax = std::fabs( face.normal.x ), ay = std::fabs( face.normal.y ), az = std::fabs( face.normal.z );
    const u32 drop = az >= ax && az >= ay ? 2u : ax >= ay ? 0u : 1u;
    return InsideAlong( wire, face, hit, drop ) ? t : -1.0;
}

// Slab test: distance to where the ray enters the box, negative on a miss.
f64 RayBox( const map_bounds_t &bounds, vec3 origin, vec3 direction ) noexcept
{
    if ( !bounds.bHas ) { return -1.0; }
    f64 near = 0.0, far = 1e300;
    for ( u32 axis = 0; axis < 3; ++axis ) {
        const f64 o = Comp( origin, axis ), d = Comp( direction, axis );
        const f64 lo = Comp( bounds.box.minimum, axis ), hi = Comp( bounds.box.maximum, axis );
        if ( std::fabs( d ) < 1e-12 ) { if ( o < lo || o > hi ) { return -1.0; } continue; }
        f64 t0 = ( lo - o ) / d, t1 = ( hi - o ) / d;
        if ( t0 > t1 ) { std::swap( t0, t1 ); }
        near = std::max( near, t0 ); far = std::min( far, t1 );
        if ( near > far ) { return -1.0; }
    }
    return near;
}

bool Pickable( const map_workspace_t *ws, const map_wire_object_t &object ) noexcept
{
    return MapWorkspace_IsVisible( ws, object ) && MapWorkspace_IsSelectableObject( ws, object );
}

map_bounds_t SelectionBounds( const map_workspace_t *ws ) noexcept
{
    map_bounds_t bounds{};
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        if ( const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] ) ) { MapBounds_AddBounds( bounds, object->bounds ); }
    }
    return bounds;
}

// Roots to move: selected objects whose owning entity is not also selected
// (moving both would move tied geometry twice).
std::vector<u64> MovableRoots( const map_workspace_t *ws )
{
    std::vector<u64> roots;
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object != nullptr && ( object->owner == 0 || !EditorSelection_Contains( &ws->selection, object->owner ) ) ) { roots.push_back( object->id ); }
    }
    return roots;
}

QString Describe( const map_workspace_t *ws, u64 id )
{
    const auto *object = MapWireframe_FindObject( ws->wire, id );
    if ( object == nullptr ) { return QStringLiteral( "#%1" ).arg( id ); }
    if ( object->kind == map_wire_kind_t::ENTITY ) {
        const auto *entity = MapWireframe_FindEntity( ws->wire, id );
        const QString name = entity != nullptr && entity->name[0] != '\0' ? QStringLiteral( " \"%1\"" ).arg( QString::fromUtf8( entity->name ) ) : QString();
        return QStringLiteral( "%1%2 #%3" ).arg( entity != nullptr ? QString::fromUtf8( entity->className ) : QStringLiteral( "entity" ), name ).arg( id );
    }
    constexpr const char *kinds[]{ "brush", "mesh", "patch", "terrain", "entity", "prefab instance" };
    return QStringLiteral( "%1 #%2" ).arg( QString::fromLatin1( kinds[static_cast<u32>( object->kind )] ) ).arg( id );
}

/*
================
Drop to Floor
================
*/

// The object's own underside above (x, y): the lowest of its faces a
// vertical line there crosses; false when the line misses the object.
bool Underside( const map_workspace_t *ws, const map_wire_object_t &object, f64 x, f64 y, f64 *pZ ) noexcept
{
    if ( object.kind == map_wire_kind_t::ENTITY ) {
        const auto &box = object.bounds.box;
        if ( x < box.minimum.x || x > box.maximum.x || y < box.minimum.y || y > box.maximum.y ) { return false; }
        *pZ = box.minimum.z;
        return true;
    }
    const auto &wire = ws->wire;
    bool found = false;
    const vec3 top{ x, y, object.bounds.box.maximum.z + 1.0 };
    for ( usize f = 0; f < wire.faces.nCount; ++f ) {
        const auto &face = wire.faces.pData[f];
        if ( face.id != object.id ) { continue; }
        const f64 t = RayFace( wire, face, top, { 0, 0, -1 } );
        if ( t < 0.0 ) { continue; }
        const f64 z = top.z - t;
        if ( !found || z < *pZ ) { *pZ = z; found = true; }
    }
    return found;
}

// The highest surface below (x, y, z) that faces up, among visible objects
// that do not move; false when there is none.
bool FloorBelow( const map_workspace_t *ws, const std::vector<u64> &moving, const map_bounds_t &footprint, f64 x, f64 y, f64 z, f64 *pFloor ) noexcept
{
    const auto &wire = ws->wire;
    bool found = false;
    const vec3 origin{ x, y, z };
    const auto movingObject = [&]( const map_wire_object_t &object ) {
        if ( std::binary_search( moving.begin(), moving.end(), object.id ) ) { return true; }
        return object.owner != 0 && std::binary_search( moving.begin(), moving.end(), object.owner );
    };
    for ( usize f = 0; f < wire.faces.nCount; ++f ) {
        const auto &face = wire.faces.pData[f];
        // Only what faces the falling object stops it (two-sided surfaces
        // stop it from either side).
        if ( !face.bTwoSided && face.normal.z <= 1e-9 ) { continue; }
        const auto *object = MapWireframe_FindObject( wire, face.id );
        if ( object == nullptr || object->kind == map_wire_kind_t::ENTITY || !MapWorkspace_IsVisible( ws, *object ) || movingObject( *object ) ) { continue; }
        const auto &box = object->bounds.box;
        if ( box.minimum.z > z || box.maximum.x < footprint.box.minimum.x || box.minimum.x > footprint.box.maximum.x ||
             box.maximum.y < footprint.box.minimum.y || box.minimum.y > footprint.box.maximum.y ) { continue; }
        const f64 t = RayFace( wire, face, origin, { 0, 0, -1 } );
        if ( t < 0.0 ) { continue; }
        const f64 floor = z - t;
        if ( !found || floor > *pFloor ) { *pFloor = floor; found = true; }
    }
    return found;
}

bool DropDistance( const map_workspace_t *ws, const map_wire_object_t &object, const std::vector<u64> &sortedMoving, f64 *pDistance ) noexcept
{
    if ( !object.bounds.bHas ) { return false; }
    const auto &box = object.bounds.box;
    // A grid across the footprint, plus the object's own corners: the first
    // place it touches is where the gap between its underside and the floor
    // is smallest.
    std::vector<std::pair<f64, f64>> samples;
    constexpr int kGrid = 7;
    const f64 inset = 1e-3 * std::max( 1.0, std::max( box.maximum.x - box.minimum.x, box.maximum.y - box.minimum.y ) );
    for ( int i = 0; i < kGrid; ++i ) {
        for ( int j = 0; j < kGrid; ++j ) {
            samples.emplace_back( box.minimum.x + inset + ( box.maximum.x - box.minimum.x - 2 * inset ) * i / ( kGrid - 1 ),
                                  box.minimum.y + inset + ( box.maximum.y - box.minimum.y - 2 * inset ) * j / ( kGrid - 1 ) );
        }
    }
    const f64 cx = 0.5 * ( box.minimum.x + box.maximum.x ), cy = 0.5 * ( box.minimum.y + box.maximum.y );
    for ( u32 p = 0; p < object.nPoints; ++p ) {
        const vec3 corner = ws->wire.points.pData[object.iFirstPoint + p];
        samples.emplace_back( corner.x + ( cx - corner.x ) * 1e-3, corner.y + ( cy - corner.y ) * 1e-3 ); // Just inside.
    }
    bool found = false;
    for ( const auto &[x, y] : samples ) {
        f64 underside = 0.0, floor = 0.0;
        if ( !Underside( ws, object, x, y, &underside ) ) { continue; }
        if ( !FloorBelow( ws, sortedMoving, object.bounds, x, y, underside + 1e-6, &floor ) ) { continue; }
        const f64 gap = std::max( 0.0, underside - floor );
        if ( !found || gap < *pDistance ) { *pDistance = gap; found = true; }
    }
    return found;
}

u32 MoveState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return State( Ready( ws ) && ObjectMode( ws ) && MapWorkspace_CanMoveSelection( ws ) );
}

struct drop_job_t {
    std::vector<std::pair<u64, f64>> moves;
    bool bTextureLock{ true };
    std::vector<u64> keep;
};

command_result_t DropToFloor( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( MoveState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    try {
        drop_job_t job;
        job.bTextureLock = ws->bTextureLock != CY_FALSE;
        job.keep.assign( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
        std::vector<u64> moving = job.keep;
        std::sort( moving.begin(), moving.end() );
        usize resting = 0, nothingBelow = 0;
        for ( u64 id : MovableRoots( ws ) ) {
            const auto *object = MapWireframe_FindObject( ws->wire, id );
            f64 distance = 0.0;
            if ( object == nullptr || object->kind == map_wire_kind_t::TERRAIN ) { continue; }
            if ( !DropDistance( ws, *object, moving, &distance ) ) { ++nothingBelow; continue; }
            if ( distance <= 1e-6 ) { ++resting; continue; }
            job.moves.emplace_back( id, distance );
        }
        if ( job.moves.empty() ) {
            MapNotice_Post( ws, map_notice_t::INFO, nothingBelow != 0 && resting == 0 ? QStringLiteral( "Drop to Floor: there is nothing below the selection to land on" )
                                                                                     : QStringLiteral( "Drop to Floor: the selection already rests on what is below it" ) );
            return command_result_t::OK;
        }
        const bool done = MapWorkspace_ApplyMetadataEdit( ws, "Drop to Floor", []( void *context, map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
            auto &j = *static_cast<drop_job_t *>( context );
            for ( const auto &[id, distance] : j.moves ) {
                const auto status = MapEdit_Translate( copy, { &id, 1 }, { 0.0, 0.0, -distance }, j.bTextureLock );
                if ( status != map_status_t::OK ) { return status; }
            }
            for ( u64 id : j.keep ) { if ( !Vector_PushBack( out, id ) ) { return map_status_t::OUT_OF_MEMORY; } }
            *changed = true;
            return map_status_t::OK;
        }, &job );
        if ( done ) {
            f64 farthest = 0.0;
            for ( const auto &move : job.moves ) { farthest = std::max( farthest, move.second ); }
            MapNotice_Post( ws, map_notice_t::SUCCESS, job.moves.size() == 1 ? QStringLiteral( "Dropped %1 by %2 units" ).arg( Describe( ws, job.moves.front().first ) ).arg( farthest )
                                                                             : QStringLiteral( "Dropped %1 objects (up to %2 units)" ).arg( job.moves.size() ).arg( farthest ) );
        }
        return Result( done );
    } catch ( ... ) { return command_result_t::FAILED; }
}

/*
================
Picking under the cursor
================
*/

template <int kStep>
command_result_t CycleUnderPointer( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pDocument == nullptr ) { return command_result_t::DISABLED; }
    try {
        const pointer_t pointer = Pointers().value( ws );
        std::vector<u64> under;
        if ( pointer.bRay ) { under = MapObjectTools_ObjectsAlongRay( ws, pointer.origin, pointer.direction ); }
        else if ( pointer.bPoint ) { under = MapObjectTools_ObjectsUnderPoint( ws, pointer.point, pointer.u, pointer.v ); }
        if ( !MapObjectTools_Cycle( ws, under, kStep ) ) {
            MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Select Next Under Cursor: nothing is under the cursor (point at overlapping objects in a view first)" ) );
            return command_result_t::OK;
        }
        return command_result_t::OK;
    } catch ( ... ) { return command_result_t::FAILED; }
}
u32 CycleState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return State( ws != nullptr && ws->pDocument != nullptr && ObjectMode( ws ) );
}

/*
================
Rotate by the angle snap
================
*/

// Left and right turn about the active view's depth axis (world Z in 3D),
// up and down tilt about its horizontal axis, by the angle snap.
template <int kHorizontal, int kVertical>
command_result_t NudgeRotate( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( MoveState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const pointer_t pointer = Pointers().value( ws );
    u32 u = 0, v = 1;
    f64 outward = 1.0;
    if ( pointer.bPoint && !pointer.bRay ) {
        u = pointer.u; v = pointer.v;
        outward = ( u == 0 && v == 2 ) ? -1.0 : 1.0; // u x v points out of the view: +Z Top, +X Front, -Y Side.
    } else if ( pointer.bRay ) {
        // 3D: turn about Z; tilt about whichever ground axis runs across the view.
        u = std::fabs( pointer.direction.y ) >= std::fabs( pointer.direction.x ) ? 0u : 1u;
        v = 2;
        outward = 1.0;
    }
    const f64 step = ws->angleSnap > 0.0 ? ws->angleSnap : 15.0;
    vec3 degrees{};
    if ( kHorizontal != 0 ) {
        const u32 depth = pointer.bRay ? 2u : 3u - u - v;
        // Left is counter-clockwise on screen: positive about the outward axis.
        SetComp( degrees, depth, -kHorizontal * step * outward );
    } else {
        SetComp( degrees, u, -kVertical * step );
    }
    vec3 pivot{};
    if ( !MapToolGestures_Pivot( ws, &pivot ) ) { pivot = MapBounds_Center( SelectionBounds( ws ) ); }
    return Result( MapWorkspace_RotateSelection( ws, degrees, pivot ) );
}

/*
================
Clear Rotation and Scale
================
*/

std::vector<u64> SelectedEntities( const map_workspace_t *ws )
{
    std::vector<u64> entities;
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const u64 id = ws->selection.ids.pData[i];
        if ( MapWireframe_FindEntity( ws->wire, id ) != nullptr ) { entities.push_back( id ); }
    }
    return entities;
}

u32 ClearRotationState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return State( Ready( ws ) && ObjectMode( ws ) && !SelectedEntities( ws ).empty() );
}

command_result_t ClearRotation( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ClearRotationState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    try {
        struct job_t { std::vector<u64> entities, keep; } job{ SelectedEntities( ws ), {} };
        job.keep.assign( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
        const bool done = MapWorkspace_ApplyMetadataEdit( ws, "Clear Rotation and Scale", []( void *context, map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
            auto &j = *static_cast<job_t *>( context );
            for ( u64 id : j.entities ) {
                map_layer_store_t *pStore = nullptr;
                key_value_t *pRecord = MapDocument_FindObject( copy, id, &pStore );
                if ( pRecord == nullptr || pStore == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
                for ( const char *pMember : { "angles", "scale" } ) {
                    if ( key_value_t *pOld = KeyValue_Find( pRecord, StringView_FromCString( pMember ) ) ) {
                        if ( !KeyValue_Remove( pStore->store.pDocument, pRecord, pOld ) ) { return map_status_t::OUT_OF_MEMORY; }
                        *changed = true;
                    }
                }
            }
            for ( u64 id : j.keep ) { if ( !Vector_PushBack( out, id ) ) { return map_status_t::OUT_OF_MEMORY; } }
            return map_status_t::OK;
        }, &job );
        if ( !done && ws->lastEdit.outcome == map_edit_outcome_t::UNCHANGED ) {
            MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Clear Rotation and Scale: the selected entities are not turned or scaled" ) );
            return command_result_t::OK;
        }
        return Result( done );
    } catch ( ... ) { return command_result_t::FAILED; }
}

/*
================
Bounding box
================
*/

u32 BoundingState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return State( Ready( ws ) && ObjectMode( ws ) && ws->selection.ids.nCount != 0 && SelectionBounds( ws ).bHas );
}

// Hammer's Create Bounding Mesh: a box exactly around the selection (for
// collision, clipping and block-out hulls), selected afterwards.
command_result_t BoundingBox( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( BoundingState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    map_bounds_t bounds = SelectionBounds( ws );
    // A flat selection (a sheet, a decal) gets the grid as its thickness.
    for ( u32 axis = 0; axis < 3; ++axis ) {
        if ( Comp( bounds.box.maximum, axis ) - Comp( bounds.box.minimum, axis ) < 1e-6 ) {
            SetComp( bounds.box.maximum, axis, Comp( bounds.box.maximum, axis ) + std::max( 1.0, static_cast<f64>( ws->gridSize ) ) );
        }
    }
    return Result( MapWorkspace_CreateBox( ws, bounds ) );
}

/*
================
Materials from the keyboard
================
*/

// The texture commands do the work; these only give their arguments.
command_result_t Run( map_workspace_t *ws, const char *pCommand, std::initializer_list<const char *> arguments ) noexcept
{
    string_view_t args[4]{};
    usize n = 0;
    for ( const char *pArgument : arguments ) { if ( n < std::size( args ) ) { args[n++] = StringView_FromCString( pArgument ); } }
    return EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( pCommand ), command_args_t{ args, n } );
}
u32 TextureState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return ws != nullptr && ws->pGui != nullptr ? EditorCommands_State( &ws->pGui->commands, StringView_FromCString( "map.texture.rotate" ) ) & COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}
template <bool kVertical>
command_result_t TextureFlip( void *pContext, const command_args_t & ) noexcept
{
    return Run( Ws( pContext ), "map.texture.scale", { kVertical ? "1" : "-1", kVertical ? "-1" : "1" } );
}
template <bool kClockwise>
command_result_t TextureTurn( void *pContext, const command_args_t & ) noexcept
{
    return Run( Ws( pContext ), "map.texture.rotate", { kClockwise ? "-90" : "90" } );
}
template <int kU, int kV>
command_result_t TextureNudge( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    const QByteArray u = QByteArray::number( kU * static_cast<f64>( ws->gridSize ) ), v = QByteArray::number( kV * static_cast<f64>( ws->gridSize ) );
    return Run( ws, "map.texture.shift", { u.constData(), v.constData() } );
}
/*
================
Snap to vertices
================
*/

bool VertexSnapOn( const map_workspace_t *ws ) noexcept
{
    return EditorSettings_Bool( &ws->pGui->settings, "editor.map.snap_to_vertices", CY_FALSE ) != CY_FALSE;
}
u32 VertexSnapState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return ws == nullptr ? COMMAND_STATE_NONE : COMMAND_STATE_ENABLED | ( VertexSnapOn( ws ) ? COMMAND_STATE_CHECKED : 0u );
}
command_result_t ToggleVertexSnap( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    settings_registry_t *settings = &ws->pGui->settings;
    const setting_descriptor_t *descriptor = EditorSettings_Find( settings, StringView_FromCString( "editor.map.snap_to_vertices" ) );
    if ( descriptor == nullptr ) { return command_result_t::FAILED; }
    setting_value_t value{};
    value.type = descriptor->type;
    value.bValue = !VertexSnapOn( ws );
    for ( usize i = 0; i < static_cast<usize>( settings_scope_t::DEFAULT ); ++i ) {
        if ( settings->scopes[i] != nullptr ) {
            const bool written = EditorSettings_Write( settings, static_cast<settings_scope_t>( i ), *descriptor, value ) == settings_registry_status_t::OK;
            if ( written ) {
                MapNotice_Post( ws, map_notice_t::INFO, value.bValue ? QStringLiteral( "Snap to Vertices on: moves lock a corner onto nearby corners (Alt or Ctrl while dragging bypasses snapping)" )
                                                                     : QStringLiteral( "Snap to Vertices off" ) );
            }
            return Result( written );
        }
    }
    return command_result_t::FAILED;
}

/*
================
Helpers
================
*/

bool HelpersShown( const map_workspace_t *ws ) noexcept
{
    return !StringView_Equals( EditorSettings_Text( &ws->pGui->settings, "editor.viewport.helpers", StringView_FromCString( "selected" ) ), StringView_FromCString( "never" ) );
}
u32 HelpersState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return ws == nullptr ? COMMAND_STATE_NONE : COMMAND_STATE_ENABLED | ( HelpersShown( ws ) ? COMMAND_STATE_CHECKED : 0u );
}
// Hammer's Show Helpers: arrows, radii, cones and target lines on or off.
command_result_t ToggleHelpers( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    settings_registry_t *settings = &ws->pGui->settings;
    const setting_descriptor_t *descriptor = EditorSettings_Find( settings, StringView_FromCString( "editor.viewport.helpers" ) );
    if ( descriptor == nullptr ) { return command_result_t::FAILED; }
    setting_value_t value{};
    value.type = descriptor->type;
    value.text = StringView_FromCString( HelpersShown( ws ) ? "never" : "selected" );
    for ( usize i = 0; i < static_cast<usize>( settings_scope_t::DEFAULT ); ++i ) {
        if ( settings->scopes[i] != nullptr ) {
            const bool written = EditorSettings_Write( settings, static_cast<settings_scope_t>( i ), *descriptor, value ) == settings_registry_status_t::OK;
            MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
            return Result( written );
        }
    }
    return command_result_t::FAILED;
}
} // namespace

void MapObjectTools_SetPointerRay( const map_workspace_t *ws, vec3 origin, vec3 direction ) noexcept
{
    pointer_t &pointer = Pointers()[ws];
    pointer.bRay = true; pointer.bPoint = false;
    pointer.origin = origin; pointer.direction = direction;
}

void MapObjectTools_SetPointerPoint( const map_workspace_t *ws, vec3 point, u32 axisU, u32 axisV ) noexcept
{
    pointer_t &pointer = Pointers()[ws];
    pointer.bPoint = true; pointer.bRay = false;
    pointer.point = point; pointer.u = axisU; pointer.v = axisV;
}

void MapObjectTools_Forget( const map_workspace_t *ws ) noexcept { Pointers().remove( ws ); Picks().remove( ws ); VertexSnaps().remove( ws ); }

bool MapObjectTools_SnapToVertices( const map_workspace_t *ws, const void *pDrag, QPointF dragStart, const map_project_fn &project, QRectF viewport,
    u32 axes, f64 pixels, vec3 *pDelta, vec3 *pTo )
{
    if ( ws == nullptr || pDelta == nullptr || !project || axes == 0u ) { return false; }
    vertex_snap_t &snap = VertexSnaps()[ws];
    snap.bSnapped = false;
    const auto &wire = ws->wire;
    const auto moving = [ws]( const map_wire_object_t &object ) {
        return EditorSelection_Contains( &ws->selection, object.id ) != CY_FALSE ||
            ( object.owner != 0 && EditorSelection_Contains( &ws->selection, object.owner ) != CY_FALSE );
    };
    // Index the corners of everything that stays where it is, as the view
    // sees them, once per drag.
    if ( snap.pDrag != pDrag || snap.start != dragStart || snap.pPoints != wire.points.pData || snap.nPoints != wire.points.nCount || snap.cell != pixels ) {
        snap = vertex_snap_t{};
        snap.pDrag = pDrag; snap.start = dragStart; snap.pPoints = wire.points.pData; snap.nPoints = wire.points.nCount; snap.cell = pixels;
        for ( usize i = 0; i < wire.objects.nCount; ++i ) {
            const auto &object = wire.objects.pData[i];
            if ( moving( object ) || !MapWorkspace_IsVisible( ws, object ) ) { continue; }
            const auto add = [&]( vec3 p ) {
                QPointF screen;
                if ( !project( p, screen ) || !viewport.adjusted( -pixels, -pixels, pixels, pixels ).contains( screen ) ) { return; }
                snap.grid[CellKey( screen, pixels )].push_back( static_cast<u32>( snap.targets.size() ) );
                snap.targets.push_back( p );
            };
            if ( object.kind == map_wire_kind_t::ENTITY ) {
                if ( const auto *entity = MapWireframe_FindEntity( wire, object.id ); entity != nullptr && entity->bHasOrigin ) { add( entity->origin ); }
                continue;
            }
            for ( u32 k = 0; k < object.nPoints; ++k ) { add( wire.points.pData[object.iFirstPoint + k] ); }
        }
    }
    if ( snap.targets.empty() ) { return false; }
    // The moving corner nearest a target, after the move so far.
    f64 best = pixels * pixels;
    vec3 from{}, to{};
    bool found = false;
    const auto test = [&]( vec3 corner ) {
        QPointF screen;
        if ( !project( Add( corner, *pDelta ), screen ) ) { return; }
        const quint64 centre = CellKey( screen, pixels );
        const qint32 cx = static_cast<qint32>( centre >> 32 ), cy = static_cast<qint32>( centre & 0xffffffffu );
        for ( qint32 dx = -1; dx <= 1; ++dx ) {
            for ( qint32 dy = -1; dy <= 1; ++dy ) {
                const auto cell = snap.grid.constFind( ( static_cast<quint64>( static_cast<quint32>( cx + dx ) ) << 32 ) | static_cast<quint32>( cy + dy ) );
                if ( cell == snap.grid.constEnd() ) { continue; }
                for ( u32 index : cell.value() ) {
                    QPointF target;
                    if ( !project( snap.targets[index], target ) ) { continue; }
                    const f64 d = ( target.x() - screen.x() ) * ( target.x() - screen.x() ) + ( target.y() - screen.y() ) * ( target.y() - screen.y() );
                    if ( d < best ) { best = d; from = corner; to = snap.targets[index]; found = true; }
                }
            }
        }
    };
    for ( usize i = 0; i < wire.objects.nCount; ++i ) {
        const auto &object = wire.objects.pData[i];
        if ( !moving( object ) || !MapWorkspace_IsVisible( ws, object ) ) { continue; }
        if ( object.kind == map_wire_kind_t::ENTITY ) {
            if ( const auto *entity = MapWireframe_FindEntity( wire, object.id ); entity != nullptr && entity->bHasOrigin ) { test( entity->origin ); }
            continue;
        }
        for ( u32 k = 0; k < object.nPoints; ++k ) { test( wire.points.pData[object.iFirstPoint + k] ); }
    }
    if ( !found ) { return false; }
    for ( u32 axis = 0; axis < 3; ++axis ) {
        if ( ( axes & ( 1u << axis ) ) != 0u ) { SetComp( *pDelta, axis, Comp( to, axis ) - Comp( from, axis ) ); }
    }
    snap.bSnapped = true; snap.to = to;
    if ( pTo != nullptr ) { *pTo = to; }
    return true;
}

bool MapObjectTools_VertexSnapTarget( const map_workspace_t *ws, vec3 *pTo ) noexcept
{
    const auto it = VertexSnaps().constFind( ws );
    if ( it == VertexSnaps().constEnd() || !it.value().bSnapped ) { return false; }
    if ( pTo != nullptr ) { *pTo = it.value().to; }
    return true;
}

void MapObjectTools_ClearVertexSnap( const map_workspace_t *ws ) noexcept { VertexSnaps().remove( ws ); }

void MapObjectTools_BeginEntityPick( map_workspace_t *ws, QObject *pOwner, map_entity_pick_fn pick )
{
    if ( ws == nullptr || !pick ) { return; }
    Picks().insert( ws, { QPointer<QObject>( pOwner ), std::move( pick ) } );
    MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Pick Entity: click the entity (or its brush) in a view; a click elsewhere cancels" ) );
}

bool MapObjectTools_EntityPickPending( const map_workspace_t *ws ) noexcept
{
    const auto it = Picks().constFind( ws );
    return it != Picks().constEnd() && !it.value().owner.isNull();
}

void MapObjectTools_CancelEntityPick( map_workspace_t *ws ) noexcept { Picks().remove( ws ); }

bool MapObjectTools_ResolveEntityPick( map_workspace_t *ws, u64 clicked )
{
    const auto it = Picks().find( ws );
    if ( it == Picks().end() ) { return false; }
    const entity_pick_t pending = it.value();
    Picks().erase( it ); // Any click ends the pick.
    if ( pending.owner.isNull() ) { return false; }
    u64 entity = MapWireframe_FindEntity( ws->wire, clicked ) != nullptr ? clicked : 0u;
    if ( entity == 0 ) {
        if ( const auto *object = MapWireframe_FindObject( ws->wire, clicked ) ) { entity = object->owner; }
    }
    const auto *picked = entity != 0 ? MapWireframe_FindEntity( ws->wire, entity ) : nullptr;
    if ( picked == nullptr ) {
        MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Pick Entity: cancelled (no entity under the click)" ) );
        return true;
    }
    if ( picked->name[0] == '\0' ) {
        MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Pick Entity: %1 has no name to target; name it in Object Properties first" ).arg( Describe( ws, entity ) ) );
        return true;
    }
    pending.pick( entity, QString::fromUtf8( picked->name ) );
    return true;
}

std::vector<u64> MapObjectTools_ObjectsAlongRay( const map_workspace_t *ws, vec3 origin, vec3 direction )
{
    std::vector<std::pair<f64, u64>> hits;
    if ( ws == nullptr ) { return {}; }
    const auto &wire = ws->wire;
    QHash<u64, f64> nearest;
    for ( usize f = 0; f < wire.faces.nCount; ++f ) {
        const auto &face = wire.faces.pData[f];
        const f64 t = RayFace( wire, face, origin, direction );
        if ( t < 0.0 ) { continue; }
        const auto it = nearest.find( face.id );
        if ( it == nearest.end() || t < it.value() ) { nearest.insert( face.id, t ); }
    }
    for ( usize i = 0; i < wire.objects.nCount; ++i ) {
        const auto &object = wire.objects.pData[i];
        if ( object.kind != map_wire_kind_t::ENTITY ) { continue; }
        const f64 t = RayBox( object.bounds, origin, direction );
        if ( t >= 0.0 ) { nearest.insert( object.id, t ); }
    }
    for ( auto it = nearest.cbegin(); it != nearest.cend(); ++it ) {
        const auto *object = MapWireframe_FindObject( wire, it.key() );
        if ( object != nullptr && Pickable( ws, *object ) ) { hits.emplace_back( it.value(), it.key() ); }
    }
    std::sort( hits.begin(), hits.end() );
    std::vector<u64> ids;
    for ( const auto &hit : hits ) { ids.push_back( hit.second ); }
    return ids;
}

std::vector<u64> MapObjectTools_ObjectsUnderPoint( const map_workspace_t *ws, vec3 point, u32 u, u32 v )
{
    std::vector<std::pair<f64, u64>> hits;
    if ( ws == nullptr ) { return {}; }
    for ( usize i = 0; i < ws->wire.objects.nCount; ++i ) {
        const auto &object = ws->wire.objects.pData[i];
        if ( !object.bounds.bHas || !Pickable( ws, object ) ) { continue; }
        const auto &box = object.bounds.box;
        const f64 pu = Comp( point, u ), pv = Comp( point, v );
        if ( pu < Comp( box.minimum, u ) || pu > Comp( box.maximum, u ) || pv < Comp( box.minimum, v ) || pv > Comp( box.maximum, v ) ) { continue; }
        const f64 area = ( Comp( box.maximum, u ) - Comp( box.minimum, u ) ) * ( Comp( box.maximum, v ) - Comp( box.minimum, v ) );
        hits.emplace_back( area, object.id );
    }
    std::sort( hits.begin(), hits.end() );
    std::vector<u64> ids;
    for ( const auto &hit : hits ) { ids.push_back( hit.second ); }
    return ids;
}

bool MapObjectTools_Cycle( map_workspace_t *ws, const std::vector<u64> &under, int step )
{
    if ( ws == nullptr || under.empty() ) { return false; }
    const usize n = under.size();
    usize next = step >= 0 ? 0u : n - 1u;
    if ( ws->selection.ids.nCount == 1 ) {
        const auto at = std::find( under.begin(), under.end(), ws->selection.ids.pData[0] );
        if ( at != under.end() ) {
            const auto index = static_cast<long long>( at - under.begin() );
            next = static_cast<usize>( ( ( index + step ) % static_cast<long long>( n ) + static_cast<long long>( n ) ) % static_cast<long long>( n ) );
        }
    }
    MapWorkspace_Select( ws, under[next], MAP_SELECT_REPLACE );
    MapNotice_Post( ws, map_notice_t::INFO, n == 1 ? QStringLiteral( "%1 is the only object under the cursor" ).arg( Describe( ws, under[next] ) )
                                                  : QStringLiteral( "%1 of %2 under the cursor: %3 (Alt+click again for the next)" ).arg( next + 1 ).arg( n ).arg( Describe( ws, under[next] ) ) );
    return true;
}

bool MapObjectTools_DropDistance( const map_workspace_t *ws, u64 id, const std::vector<u64> &moving, f64 *pDistance )
{
    if ( ws == nullptr || pDistance == nullptr ) { return false; }
    const auto *object = MapWireframe_FindObject( ws->wire, id );
    if ( object == nullptr ) { return false; }
    std::vector<u64> sorted = moving;
    std::sort( sorted.begin(), sorted.end() );
    return DropDistance( ws, *object, sorted, pDistance );
}

command_registry_status_t MapObjectTools_Register( map_workspace_t *pWorkspace, command_registry_t *pRegistry ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && pRegistry != nullptr );
    constexpr u32 kEdit = COMMAND_FLAG_EDITS_DOCUMENT;
    const command_desc_t commands[]{
        { "map.transform.drop_to_floor", "Drop to Floor", "Move each selected object straight down until it rests on the surface below it (Hammer's Move Object Down by Trace).", "tool-translate", nullptr,
          kEdit, DropToFloor, MoveState, pWorkspace },
        { "map.select.cycle_next", "Select Next Under Cursor", "Select the next object under the cursor, farther away (Alt+click in a view does the same).", "select-objects", nullptr,
          COMMAND_FLAG_NONE, CycleUnderPointer<1>, CycleState, pWorkspace },
        { "map.select.cycle_previous", "Select Previous Under Cursor", "Select the previous object under the cursor, nearer to you.", "select-objects", nullptr,
          COMMAND_FLAG_NONE, CycleUnderPointer<-1>, CycleState, pWorkspace },
        { "map.nudge_rotate.left", "Rotate Left by Angle Snap", "Turn the selection counter-clockwise in the active view by the angle snap (about the Pivot when one is placed).", "tool-rotate", nullptr,
          kEdit, NudgeRotate<-1, 0>, MoveState, pWorkspace },
        { "map.nudge_rotate.right", "Rotate Right by Angle Snap", "Turn the selection clockwise in the active view by the angle snap.", "tool-rotate", nullptr,
          kEdit, NudgeRotate<1, 0>, MoveState, pWorkspace },
        { "map.nudge_rotate.up", "Tilt Up by Angle Snap", "Tilt the selection about the active view's horizontal axis by the angle snap.", "tool-rotate", nullptr,
          kEdit, NudgeRotate<0, 1>, MoveState, pWorkspace },
        { "map.nudge_rotate.down", "Tilt Down by Angle Snap", "Tilt the selection the other way about the active view's horizontal axis.", "tool-rotate", nullptr,
          kEdit, NudgeRotate<0, -1>, MoveState, pWorkspace },
        { "map.transform.clear_rotation", "Clear Rotation and Scale", "Turn the selected entities back to no rotation and their natural size.", "tool-rotate", nullptr,
          kEdit, ClearRotation, ClearRotationState, pWorkspace },
        { "map.brush.bounding_box", "Create Bounding Box", "Make a box brush exactly around the selection (Hammer's Create Bounding Mesh): collision hulls, clip volumes, block-outs.", "tool-block", nullptr,
          kEdit, BoundingBox, BoundingState, pWorkspace },
        { "map.texture.flip_horizontal", "Flip Material Horizontally", "Mirror the material left to right on the picked faces or selected objects.", "uv-scale", nullptr,
          kEdit, TextureFlip<false>, TextureState, pWorkspace },
        { "map.texture.flip_vertical", "Flip Material Vertically", "Mirror the material top to bottom on the picked faces or selected objects.", "uv-scale", nullptr,
          kEdit, TextureFlip<true>, TextureState, pWorkspace },
        { "map.texture.rotate_cw", "Rotate Material 90 Clockwise", "Turn the material a quarter clockwise on the picked faces or selected objects.", "uv-rotate", nullptr,
          kEdit, TextureTurn<true>, TextureState, pWorkspace },
        { "map.texture.rotate_ccw", "Rotate Material 90 Counter-Clockwise", "Turn the material a quarter counter-clockwise.", "uv-rotate", nullptr,
          kEdit, TextureTurn<false>, TextureState, pWorkspace },
        { "map.texture.nudge_left", "Nudge Material Left", "Shift the material left by the grid size.", "uv-shift", nullptr,
          kEdit, TextureNudge<-1, 0>, TextureState, pWorkspace },
        { "map.texture.nudge_right", "Nudge Material Right", "Shift the material right by the grid size.", "uv-shift", nullptr,
          kEdit, TextureNudge<1, 0>, TextureState, pWorkspace },
        { "map.texture.nudge_up", "Nudge Material Up", "Shift the material up by the grid size.", "uv-shift", nullptr,
          kEdit, TextureNudge<0, 1>, TextureState, pWorkspace },
        { "map.texture.nudge_down", "Nudge Material Down", "Shift the material down by the grid size.", "uv-shift", nullptr,
          kEdit, TextureNudge<0, -1>, TextureState, pWorkspace },
        { "map.grid.snap_vertices", "Snap to Vertices", "Moving objects locks a corner of the selection onto a nearby corner of other geometry (Hammer).", "snap-grid", nullptr,
          COMMAND_FLAG_CHECKABLE, ToggleVertexSnap, VertexSnapState, pWorkspace },
        { "map.show.helpers", "Show Helpers", "Draw entity helpers (facing arrows, light radii, spot cones, target lines, door travel) for the selection, or not at all.", "entity-light", nullptr,
          COMMAND_FLAG_CHECKABLE, ToggleHelpers, HelpersState, pWorkspace },
    };
    return EditorCommands_Register( pRegistry, commands, std::size( commands ) );
}

} // namespace cypher::editor::map
